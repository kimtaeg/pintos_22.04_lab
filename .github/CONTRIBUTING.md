# 협업 가이드

jungle-pintos 레포에서 함께 작업할 때 지키는 규칙입니다.
처음 합류했다면 이 문서를 끝까지 한 번 읽어주세요.

## 1. 브랜치 구조

| 브랜치        | 용도                                              | 직접 push   |
| ------------- | ------------------------------------------------- | ----------- |
| `main`        | 완성된 단계(프로젝트 제출 기준)만 들어가는 브랜치 | 불가 (PR만) |
| `develop`     | 작업 내용이 모이는 통합 브랜치, 기본 브랜치       | 불가 (PR만) |
| `feat/...` 등 | 개인 작업 브랜치                                  | 가능        |

- `main`, `develop`은 보호 규칙이 걸려 있어 **PR로만 merge** 할 수 있습니다.
- 모든 작업은 `develop`에서 새 브랜치를 만들어 시작합니다.
- `develop` → `main` merge는 한 프로젝트 단계(threads, userprog, vm 등)가 끝났을 때 함께 확인하고 진행합니다.

## 2. 브랜치 이름 규칙

```
<타입>/<작업-내용>
```

| 타입        | 언제                                                            |
| ----------- | --------------------------------------------------------------- |
| `feat/`     | 새 기능 구현 (예: `feat/alarm-clock`, `feat/priority-donation`) |
| `fix/`      | 버그 수정 (예: `fix/sema-up-race`)                              |
| `refactor/` | 동작 변화 없는 코드 정리                                        |
| `test/`     | 테스트 관련 작업                                                |
| `docs/`     | 문서 수정 (예: `docs/contributing`)                             |
| `chore/`    | 빌드, 설정, Docker 등 기타 작업                                 |

- 소문자와 하이픈(`-`)만 사용합니다.
- 한 브랜치에는 한 가지 작업만 담습니다.

## 3. 작업 흐름

### 처음 한 번

```bash
git clone https://github.com/yongminkim0501/jungle-pintos.git
cd jungle-pintos
git checkout develop
```

### 새 작업 시작할 때

```bash
# 1. develop 최신화
git checkout develop
git pull origin develop

# 2. 작업 브랜치 생성
git checkout -b feat/alarm-clock

# 3. 작업 후 커밋
git add .
git commit -m "feat: alarm clock busy waiting 제거"

# 4. 원격에 push
git push origin feat/alarm-clock
```

5. GitHub에서 **`feat/alarm-clock` → `develop`** 으로 PR을 엽니다.
   (base가 `main`으로 잡혀 있지 않은지 꼭 확인!)

### 작업 중 develop이 업데이트됐을 때

```bash
git fetch origin
git rebase origin/develop
# 충돌 나면 해결 후 git add → git rebase --continue
git push -f origin feat/alarm-clock
```

- `push -f`는 **내 작업 브랜치에만** 사용합니다. `main`, `develop`에는 절대 사용하지 않습니다.

### PR이 merge된 후

```bash
git checkout develop
git pull origin develop
git branch -d feat/alarm-clock
```

- 원격 브랜치는 PR 화면의 **Delete branch** 버튼으로 지웁니다.

## 4. 커밋 메시지 규칙

```
<타입>: <무엇을 했는지>
```

- 타입은 브랜치 타입과 같습니다: `feat`, `fix`, `refactor`, `test`, `docs`, `chore`
- 내용은 한글로 짧고 구체적으로 씁니다.

```
feat: thread_sleep, thread_awake 구현
fix: priority donation 시 lock holder 우선순위 갱신 누락 수정
refactor: ready_list 정렬 비교 함수 분리
```

- 의미 없는 메시지(`수정`, `asdf`, `wip`)는 피합니다.
- 커밋은 "되돌려도 빌드가 깨지지 않는 단위"로 나누면 좋습니다.

## 5. PR 규칙

- PR 하나에는 작업 하나만 담습니다. 리뷰하기 쉬운 크기를 유지해 주세요.
- PR을 열면 자동으로 채워지는 템플릿을 빠짐없이 작성합니다.
- PR을 올리기 전에 **빌드와 관련 테스트가 통과하는지** 직접 확인합니다.
- 리뷰 코멘트는 같은 브랜치에 커밋을 추가로 push해서 반영합니다. (PR을 새로 만들 필요 없음)
- merge 방식은 **Squash and merge** 를 기본으로 합니다.
- merge는 PR 작성자가 하되, 상대방이 확인한 뒤에 합니다.

## 6. 테스트 확인 방법

```bash
cd threads
make clean && make
cd build
make check
```

- 특정 테스트만 돌려볼 때:

```bash
make tests/threads/alarm-multiple.result
```

- PR 본문에 통과한 테스트 수(예: `27 of 27 tests passed`)를 적어주세요.
